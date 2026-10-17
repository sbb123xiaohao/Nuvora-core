function assert(value) { if (!value) throw new Error('check failed'); }
const blocks = new Uint8Array(16 * 16);
let seed = 21, checksum = 0;
for (let i=0; i<blocks.length; i++) {
  seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
  blocks[i] = seed % 64;
  checksum += blocks[i];
}
assert(checksum === 8064);
class Block { constructor(id) { this.id=id; } score(n) { return this.id*n; } }
assert(new Block(7).score(3)===21);
assert((2n ** 80n).toString()==='1208925819614629174706176');
assert(/world/.test('hello world') && Math.sqrt(81)===9);
const values=new Map([['chunk',checksum]]);assert(values.get('chunk')===8064);
let caught=false;try { throw new Error('caught'); } catch(error) { caught=error.message==='caught'; }assert(caught);
print('QuickJS / classes, typed arrays, BigInt, Map, errors and promises');
print(`chunk=16x16, checksum=${checksum}`);
print(`arg=${scriptArgs[1] ?? 'none'}`);
Promise.resolve(21).then(value=>{assert(value===21);print('JAVASCRIPT RUNTIME PASS');});
